import style from './print.module.scss'
import { Stack, Typography } from '@mui/material'
import React, { useEffect, useState } from 'react'
import { doGetRequest } from '../../Common/StaticFunctionsTyped'
import { dateToString } from '../../Common/StaticFunctions'
import { useParams } from 'react-router-dom'
import { useDispatch } from 'react-redux'

type Props = {}

const PageHeader = (props: Props) => {

    const [date, setdate] = useState("")
    const [tournamentName, settournamentName] = useState("")
    const [judge, setjudge] = useState("")
    const [organiationName, setorganiationName] = useState("")

    const params = useParams()
    const dispatch = useDispatch()

    useEffect(() => {

        doGetRequest(`${params.organization}/${params.date}/info`, dispatch).then((response) => {
            if (response.code === 200) {
                const respJson: any = response.content
                setdate(dateToString(new Date(respJson.date)))
                settournamentName(respJson.tournamentName)
                setjudge(respJson.judge)
                setorganiationName(respJson.organization)
            }
        })

    }, [params.date, params.organization, dispatch])

    return (
        <Stack direction="row" justifyContent="space-between">
            <Stack direction="column" alignItems="center" gap={1}>
                <img
                    src={`/Logo-Simple-black.svg`}
                    className={style.logo}
                    loading="eager"
                    alt="Logo"
                />
                <Typography align="center" className={style.text}>
                    DogDog Turnier
                </Typography>
            </Stack >
            <Stack direction="column" alignItems="center" gap={1}>
                <Typography variant='h5' align="center" >
                    {tournamentName} - {date}
                </Typography>
                <Typography variant='h6' align="center" >
                    {organiationName}
                </Typography>
                <Typography variant='h6' align="center" >
                    Richter: {judge}
                </Typography>
            </Stack>
            <div className={style.logo}></div>
        </Stack >
    )
}

export default PageHeader