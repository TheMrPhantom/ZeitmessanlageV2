import { Button, Dialog, DialogActions, DialogContent, DialogContentText, DialogTitle, Stack, Typography } from '@mui/material'
import React from 'react'
import Spacer from '../../Common/Spacer'
import { useNavigate } from 'react-router-dom'

import WarningIcon from '@mui/icons-material/Warning';
import style from './useOffline.module.scss'
import { doGetRequest } from '../../Common/StaticFunctions';
import { useDispatch } from 'react-redux';
import { loadOrganization } from '../../../Actions/SampleAction';
import { storePermanent } from '../../Common/StaticFunctionsTyped';


type Props = {
    type: "online" | "device",
    isOpen: boolean,
    close: () => void
}

const OfflineDialog = (props: Props) => {
    const dispatch = useDispatch()
    const navigate = useNavigate();

    return (
        <Dialog open={props.isOpen} onClose={props.close} sx={{ zIndex: 20000000 }} >
            <DialogTitle>{props.type === "online" ? "Onlinedaten verwenden" : "Gerätedaten verwenden"}</DialogTitle>
            <DialogContent >
                <DialogContentText>
                    {props.type === "online" ? "Bist du dir sicher, dass du die online Daten verwenden möchtest?" :
                        "Bist du dir sicher, dass du die auf dem Gerät gespeicherten Daten verwenden möchtest?"}
                </DialogContentText>
                <Spacer vertical={20} />
                <Stack direction="row" gap={2} flexWrap="wrap" >
                    <WarningIcon color='warning' />
                    <Typography variant="body1">Diese Aktion kann nicht rückgängig gemacht werden</Typography>
                    <WarningIcon color='warning' />
                </Stack>

            </DialogContent>
            <DialogActions>
                <Stack direction="row" justifyContent="space-between" className={style.buttonContainer}>
                    <Button onClick={props.close} variant='outlined'>Abbrechen</Button>
                    <Button onClick={() => {
                        if (props.type === "online") {
                            const localValidationData = localStorage.getItem("validation")
                            if (localValidationData) {
                                const organization = JSON.parse(localValidationData).name
                                if (organization.name !== "") {
                                    doGetRequest(`${organization}`).then((response) => {
                                        if (response.code === 200) {
                                            dispatch(loadOrganization(response.content))
                                            storePermanent(organization, response.content)
                                            props.close()
                                            navigate(`/`)
                                        }
                                    })
                                }
                            }
                        }

                    }} variant='contained'>
                        {props.type === "online" ? "Onlinedaten" : "Gerätedaten"}
                    </Button>
                </Stack>
            </DialogActions>
        </Dialog >
    )
}

export default OfflineDialog