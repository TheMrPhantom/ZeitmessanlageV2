import React, { useEffect, useState } from 'react'
import { Member } from '../../../types/ResponseTypes'
import { doGetRequest, doPostRequest, doRequest } from '../../Common/StaticFunctionsTyped'
import { useDispatch } from 'react-redux'
import { Accordion, AccordionActions, AccordionDetails, AccordionSummary, Button, Stack, TextField, Typography } from '@mui/material'
import style from './admin.module.scss'
import { LocalizationProvider } from '@mui/x-date-pickers/LocalizationProvider/LocalizationProvider'
import { DateTimePicker } from '@mui/x-date-pickers/DateTimePicker';
import { AdapterDateFns } from '@mui/x-date-pickers/AdapterDateFns'
import { de } from 'date-fns/locale'
import ExpandMoreIcon from '@mui/icons-material/ExpandMore';
import DeleteMember from './DeleteMember'
import { openToast } from '../../../Actions/CommonAction'

type Props = {}

const Admin = (props: Props) => {
    const [members, setmembers] = useState<Member[]>([])
    const dispatch = useDispatch()

    const [expanded, setexpanded] = useState(false)

    //Usestate for all text and date fields
    const [name, setname] = useState<string>("")
    const [password, setpassword] = useState<string>("")
    const [displayname, setdisplayname] = useState<string>("")
    const [reference, setreference] = useState<string>("")
    const [validUntil, setvalidUntil] = useState<Date>(new Date())
    const [toDelete, settoDelete] = useState<null | string>(null)
    const [createDisabled, setcreateDisabled] = useState(false)

    useEffect(() => {
        doGetRequest("members", dispatch).then((value) => {
            if (value.code === 200) {
                setmembers(value.content)
            }
        })
    }, [dispatch])

    return (
        <>
            <Stack direction="column" gap={3} className={style.container}>
                <Accordion expanded={expanded} onChange={() => { setexpanded(!expanded) }}>
                    <AccordionSummary
                        expandIcon={<ExpandMoreIcon />}
                    >
                        Neuen Verein anlegen
                    </AccordionSummary>
                    <AccordionDetails>
                        <Stack gap={3}>
                            <TextField
                                error={name === ""}
                                required
                                value={name}
                                size='small'
                                label="Vereinskürzel (URL)"
                                onChange={(value) => {
                                    setname(value.target.value)
                                }}
                            />
                            <TextField
                                error={displayname === ""}
                                required
                                value={displayname}
                                size='small'
                                label="Anzeigename"
                                onChange={(value) => {
                                    setdisplayname(value.target.value)
                                }}
                            />
                            <TextField
                                error={password === ""}
                                required
                                value={password}
                                size='small'
                                label="Passwort"
                                type='password'
                                onChange={(value) => {
                                    setpassword(value.target.value)
                                }}
                            />
                            <TextField
                                value={reference}
                                size='small'
                                label="Referenz"
                                onChange={(value) => {
                                    setreference(value.target.value)
                                }}
                            />
                            <LocalizationProvider dateAdapter={AdapterDateFns} adapterLocale={de}>
                                <DateTimePicker
                                    label="Validiert bis"
                                    value={validUntil}
                                    onChange={(value) => {
                                        if (value) {
                                            setvalidUntil(value)
                                        }
                                    }} />
                            </LocalizationProvider>
                        </Stack>
                    </AccordionDetails>
                    <AccordionActions>
                        <Button onClick={() => { setexpanded(false) }}>Abbrechen</Button>
                        <Button variant='contained'
                            disabled={createDisabled}
                            onClick={() => {
                                //Check if all text field and pickers are filled, if not show warning toast
                                if (name === "" || password === "" || displayname === "" || !validUntil) {
                                    dispatch(openToast({ message: "Bitte fülle alle Felder aus", type: "warning", headline: "Anlegen fehlgeschlagen" }))
                                    return
                                }

                                setcreateDisabled(true)
                                doRequest("PUT", "members", {
                                    name: name,
                                    password: password,
                                    alias: displayname,
                                    reference: reference,
                                    verifiedUntil: validUntil.toISOString()
                                }, dispatch).then((value) => {
                                    if (value.code === 200) {
                                        //Clear text fields
                                        setname("")
                                        setpassword("")
                                        setdisplayname("")
                                        setreference("")
                                        setvalidUntil(new Date())
                                        setmembers(value.content)
                                    } else if (value.code === 409) {
                                        dispatch(openToast({ message: "Verein existiert bereits", type: "error", headline: "Anlegen fehlgeschlagen" }))
                                    }
                                    setcreateDisabled(false)
                                })

                            }}
                        >Anlegen</Button>
                    </AccordionActions>
                </Accordion>
                <Typography variant="h4">Vereine</Typography>
                {/* Map the members to a list of papers with textfields and pickers for chaning their values */}
                <Stack direction="column">
                    {members.map((member) => {
                        return (
                            <Accordion key={member.name} className={style.paper}>
                                <AccordionSummary
                                    expandIcon={<ExpandMoreIcon />}
                                >
                                    <Typography variant="h5">
                                        {member.alias}
                                    </Typography>
                                </AccordionSummary>
                                <AccordionDetails>
                                    <Stack direction="column" gap={3}>
                                        <TextField
                                            value={member.alias}
                                            size='small'
                                            label="Anzeigename"
                                            onChange={(value) => {
                                                doPostRequest("member/alias", { name: member.name, value: value.target.value }, dispatch).then((value) => {
                                                    if (value.code === 200) {
                                                        setmembers(value.content)
                                                    }
                                                })
                                            }}
                                        />
                                        <TextField
                                            size='small'
                                            label="Passwort"
                                            type='password'
                                            onChange={(value) => {
                                                doPostRequest("member/password", { name: member.name, value: value.target.value }, dispatch).then((value) => {
                                                    if (value.code === 200) {
                                                        setmembers(value.content)
                                                    }
                                                })
                                            }}
                                        />
                                        <TextField
                                            value={member.reference}
                                            size='small'
                                            label="Referenz"
                                            onChange={(value) => {
                                                doPostRequest("member/reference", { name: member.name, value: value.target.value }, dispatch).then((value) => {
                                                    if (value.code === 200) {
                                                        setmembers(value.content)
                                                    }
                                                })
                                            }}
                                        />
                                        <LocalizationProvider dateAdapter={AdapterDateFns} adapterLocale={de}>
                                            <DateTimePicker
                                                label="Validiert bis"
                                                value={new Date(member.verifiedUntil)}
                                                onChange={(value) => {
                                                    if (value) {
                                                        doPostRequest("member/verifiedUntil", { name: member.name, value: value.toISOString() }, dispatch).then((value) => {
                                                            if (value.code === 200) {
                                                                setmembers(value.content)
                                                            }
                                                        })
                                                    }
                                                }} />
                                        </LocalizationProvider>

                                    </Stack>
                                </AccordionDetails>
                                <AccordionActions>
                                    <Button variant='outlined' color='error'
                                        onClick={() => {
                                            settoDelete(member.name)
                                        }}
                                    >Löschen</Button>
                                </AccordionActions>
                            </Accordion>
                        )
                    })}
                </Stack>
            </Stack>
            <DeleteMember isOpen={toDelete !== null} close={() => {
                doGetRequest("members", dispatch).then((value) => {
                    if (value.code === 200) {
                        setmembers(value.content)
                    }
                });
                settoDelete(null);
            }} memberName={toDelete} memberAlias={displayname} />
        </>
    )
}

export default Admin